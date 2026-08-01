extends SceneTree

## THE WAR OF THE RING OPPONENT.
##
## The owner's complaint this file answers: "there is no good way to just play
## the game with some ai". Seats were marked `ai` in the standings and did
## NOTHING on their turn, so the strategic layer cycled the turn index and handed
## control straight back. What is pinned here is that an AI seat now takes a
## turn, that the turn is the SAME turn on every machine, and that it cannot do
## anything the rules would refuse a human.
##
## FOUR PROPERTIES, and they are the four the whole design rests on:
##
##   1. AN AI SEAT'S TURN CHANGES THE BOARD. The strategic hash before the turn
##      and after it differ, and the turn index advanced exactly once.
##   2. THE SAME STATE AND THE SAME SEED PRODUCE THE SAME DECISIONS. Two
##      independently built sessions on identical state produce byte-identical
##      reports and identical hashes. There is no RNG on the path and the source
##      is READ to prove it.
##   3. THE AI CANNOT MAKE A MOVE THE RULES WOULD REFUSE A HUMAN. Every target it
##      picks passes `state.can_attack`, every march is a single graph edge into
##      a region the seat already owns, and every mutation goes through the same
##      session doors the screen's buttons use.
##   4. THE STRATEGIC HASH STILL ROUND-TRIPS AFTER AN AI TURN. Snapshot, restore,
##      same hash - the AI's moves are ordinary authoritative-state mutations,
##      not a side channel.
##
## Run:
##   Godot_v4.7 --headless --path game --script res://tests/wotr_ai_runner.gd
##
## The fixture below is authored HERE, not extracted from retail: the same
## document SHAPE the importer emits, with invented region names, and a
## hand-written AI template ini with invented weights. No retail payload is
## packaged with this test. The second half runs only when the real living-world
## document and auto-resolve tables are staged, and its checks are counted
## separately so a machine without them still gets a LIVENESS number it can fail.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const AiScript = preload("res://src/wotr/wotr_ai.gd")
const AutoResolveScript = preload("res://src/wotr/wotr_autoresolve.gd")
const BindingsScript = preload("res://src/wotr/wotr_autoresolve_bindings.gd")

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every check after the error site never runs and an
## inert runner prints zero failures and exits 0. Pinning the count turns that
## silent abort into a loud failure. Raise deliberately; never lower.
## 79 -> 81 / 92 -> 94: retail's four `BuildingScore*` weights are SPENT now
## (bound to retail's own four `Type` values, `Castle`->`Fortress` and
## `Farm`->`Resource`) and `BonusPreferenceTreasury` is bound to
## `fertileTerritory`, so the two checks that used to pin those as UNSPENDABLE
## now pin the bindings instead, and two new ones pin the weights themselves and
## the provenance row that reports what each one buys.
const CHECKS_WITHOUT_DATA := 81
const CHECKS_WITH_DATA := 94

## The maps the tactical layer can boot, as far as this runner is concerned. Any
## non-empty list binds every region's battlefield; the ids are never launched.
const HARNESS_MAPS := ["harness_map_a", "harness_map_b"]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	print("== WAR OF THE RING: THE OPPONENT ==")

	_test_retail_template_parses()
	_test_retail_template_fails_closed()
	_test_nothing_on_this_path_can_read_a_clock_or_an_rng()
	_test_project_authored_rules_are_named()
	_test_an_ai_seat_takes_a_turn_and_the_board_changes()
	_test_the_same_state_and_seed_decide_the_same_twice()
	_test_the_ai_cannot_move_where_a_human_could_not()
	_test_retail_weights_choose_the_target()
	_test_the_hash_round_trips_after_an_ai_turn()
	_test_the_turn_is_visible()
	_test_the_ai_refuses_a_seat_that_is_not_its_own()

	var have_data := _test_against_retail_data()

	var expected := CHECKS_WITH_DATA if have_data else CHECKS_WITHOUT_DATA
	var ran := passed + failed
	if ran != expected:
		failed += 1
		printerr("WOTR_AI FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, expected])
	print("WOTR_AI_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- retail's AI template ------------------------------------------------------

## The ini this parser is written against, in retail's own shape: one
## `LivingWorldAITemplate <name>` block, `Key = Integer` bodies, `;` comments,
## trailing blank lines after `End`. The WEIGHTS are invented for the test; the
## SHAPE is retail's.
const FIXTURE_TEMPLATE := """;//////////////////////////////////////////////////
;// Living World AI Templates
;//////////////////////////////////////////////////

LivingWorldAITemplate HarnessAITemplate

		DesiredSoldierRatio = 25
		DesiredHeroRatio = 75

		BuildingScoreArmory = 50
		BuildingScoreBarracks = 41
		BuildingScoreCastle = 42
		BuildingScoreFarm = 43

		BonusPreferenceResource = 1
		BonusPreferenceArmy = 1
		BonusPreferenceLegendary = 4
		BonusPreferenceAttack = 1
		BonusPreferenceDefense = 1
		BonusPreferenceExperience = 1
		BonusPreferenceTreasury = 6
End


"""


func _write_template(name: String, text: String) -> String:
	var path := "user://%s" % name
	var handle := FileAccess.open(path, FileAccess.WRITE)
	handle.store_string(text)
	handle.close()
	return path


func _loaded_template() -> AiScript:
	var ai := AiScript.new()
	ai.load_from(_write_template("wotr_ai_fixture.ini", FIXTURE_TEMPLATE))
	return ai


func _test_retail_template_parses() -> void:
	var ai := _loaded_template()
	_check("the_template_loads", ai.loaded)
	_check("the_template_keeps_retails_own_block_name", ai.template_name == "HarnessAITemplate",
		ai.template_name)
	_check("every_authored_weight_is_carried_verbatim",
		ai.values.size() == 13 and int(ai.values.get("BonusPreferenceLegendary", -1)) == 4
			and int(ai.values.get("BonusPreferenceTreasury", -1)) == 6
			and int(ai.values.get("DesiredHeroRatio", -1)) == 75,
		str(ai.values))
	# A key retail did not author is ABSENT, not zero-with-confidence.
	_check("an_unauthored_preference_weighs_nothing",
		ai.preference("BonusPreferenceNotAThing") == 0)

	# THE WEIGHTS THIS LAYER CANNOT SPEND ARE REPORTED, not dropped. Each names
	# the gap in the register that is why.
	var unspendable := ai.unspendable_weights()
	_check("recruitment_weights_are_reported_against_their_gap",
		String(unspendable.get("DesiredHeroRatio", "")) == "army_recruitment_and_cp_costs",
		str(unspendable))
	# RESTATED, NOT DELETED. This check used to require `BuildingScoreArmory` to
	# be reported UNSPENDABLE against `strategic_building_construction`. That gap
	# is closed - retail's four `BuildingScore*` weights now choose what the
	# opponent builds - so the old expectation no longer holds and the new one pins
	# the opposite intent: those weights must NOT be listed unspendable, and they
	# must be bound to retail's four Type values.
	_check("construction_weights_are_no_longer_reported_unspendable",
		not unspendable.has("BuildingScoreArmory")
			and not unspendable.has("BuildingScoreCastle"), str(unspendable))
	_check("bonus_preferences_are_not_reported_unspendable",
		not unspendable.has("BonusPreferenceLegendary"), str(unspendable))
	_check("retail_building_weights_buy_retails_own_four_types",
		# Four DISTINCT invented weights, so the two bindings that are not the same
		# word twice are each proved on their own: retail's `BuildingScoreCastle`
		# must reach `Type = Fortress` and its `BuildingScoreFarm` must reach
		# `Type = Resource`.
		ai.building_score("Armory") == 50 and ai.building_score("Barracks") == 41
			and ai.building_score("Fortress") == 42 and ai.building_score("Resource") == 43,
		"%d %d %d %d" % [ai.building_score("Armory"), ai.building_score("Barracks"),
			ai.building_score("Fortress"), ai.building_score("Resource")])

	# RESTATED FOR THE SAME REASON. Retail's highest weight used to be named
	# deliberately UNBOUND, on the grounds that "the living-world document records
	# no region-bonus field called treasury". It records `fertileTerritory`, and
	# retail's own display string for that field is `LW:RegionTreasuryBonus`, "+%d
	# Treasure". The weight is spent now; what is pinned is that the unbound table
	# is EMPTY and the binding exists, so nobody can quietly drop either.
	_check("no_retail_preference_is_left_unbound",
		AiScript.BONUS_PREFERENCE_UNBOUND.is_empty())
	_check("the_treasury_preference_is_bound_to_the_fertile_territory_field",
		String(AiScript.BONUS_PREFERENCE_FIELDS.get("BonusPreferenceTreasury", ""))
			== "fertileTerritory")

	var provenance := ai.decision_provenance()
	_check("provenance_reports_the_template_as_loaded",
		bool(provenance.get("retail_template_loaded", false))
			and (provenance.get("retail_weights_bound", {}) as Dictionary).size() == 7,
		str(provenance.get("retail_weights_bound", {})))
	_check("provenance_reports_the_building_weights_and_what_each_buys",
		(provenance.get("retail_building_scores", {}) as Dictionary).size() == 4
			and String(((provenance["retail_building_scores"] as Dictionary)["BuildingScoreCastle"]
				as Dictionary)["type"]) == "Fortress",
		str(provenance.get("retail_building_scores", {})))


func _test_retail_template_fails_closed() -> void:
	# A HALF-PARSED PREFERENCE ORDERING RANKS REGIONS IN AN ORDER NEITHER RETAIL
	# NOR THIS PROJECT CHOSE, so every malformation refuses the whole file.
	var cases := {
		"a_body_line_that_is_not_key_equals_value":
			"LivingWorldAITemplate T\n\tBonusPreferenceArmy\nEnd\n",
		"a_value_that_is_not_a_whole_number":
			"LivingWorldAITemplate T\n\tBonusPreferenceArmy = 1.5\nEnd\n",
		"a_block_that_never_ends":
			"LivingWorldAITemplate T\n\tBonusPreferenceArmy = 1\n",
		"a_block_with_no_name":
			"LivingWorldAITemplate\n\tBonusPreferenceArmy = 1\nEnd\n",
		"a_second_block":
			"LivingWorldAITemplate T\n\tBonusPreferenceArmy = 1\nEnd\n"
			+ "LivingWorldAITemplate U\n\tBonusPreferenceArmy = 2\nEnd\n",
		"a_duplicate_key":
			"LivingWorldAITemplate T\n\tBonusPreferenceArmy = 1\n\tBonusPreferenceArmy = 2\nEnd\n",
		"an_empty_block":
			"LivingWorldAITemplate T\nEnd\n",
		"a_file_that_is_not_this_ini_at_all":
			"Object GondorSoldier\n\tArmor = Foo\nEnd\n",
	}
	for name in cases.keys():
		var ai := AiScript.new()
		var path := _write_template("wotr_ai_bad.ini", String(cases[name]))
		_check("the_template_refuses_%s" % String(name), not ai.load_from(path) and not ai.loaded)

	# A MISSING TEMPLATE IS NOT A CRASH AND NOT A SILENT ZERO: the opponent still
	# plays, and the reason names the file, the setting, and what is lost.
	var absent := AiScript.new()
	# The override is lifted for this one check, so the result does not depend on
	# whether the machine running the suite happens to have retail staged.
	var restore := OS.get_environment(AiScript.BUNDLE_ENV)
	OS.set_environment(AiScript.BUNDLE_ENV, "")
	var located := absent.locate_and_load(["res://tests/there-is-no-such-directory"])
	OS.set_environment(AiScript.BUNDLE_ENV, restore)
	_check("a_missing_template_reports_what_the_opponent_loses",
		not bool(located.get("ok", false))
			and String(located.get("reason", "")).contains(AiScript.BUNDLE_ENV)
			and String(located.get("reason", "")).contains(AiScript.FILE_NAME),
		String(located.get("reason", "")))


## THE DETERMINISM PROOF THAT DOES NOT DEPEND ON RUNNING ANYTHING. A future edit
## that reaches for `randf()` or a clock to break a tie would pass every
## behavioural check on one machine and desynchronise two. The source is read.
func _test_nothing_on_this_path_can_read_a_clock_or_an_rng() -> void:
	var handle := FileAccess.open("res://src/wotr/wotr_ai.gd", FileAccess.READ)
	var text := handle.get_as_text()
	handle.close()
	var forbidden := [
		"randf(", "randi(", "randomize(", "rand_from_seed(", "RandomNumberGenerator",
		"Time.get_", "OS.get_ticks", "Engine.get_frames",
	]
	for token in forbidden:
		# The token appears in this list and in the file's own comment about not
		# using them; the check is that no LINE OF CODE calls one.
		var offending := ""
		for line in text.split("\n"):
			var code := String(line).strip_edges()
			if code.begins_with("#") or code.begins_with("##"):
				continue
			if code.contains(String(token)):
				offending = code
				break
		_check("the_opponent_never_calls_%s" % String(token).replace("(", "").replace(".", "_"),
			offending.is_empty(), offending)


func _test_project_authored_rules_are_named() -> void:
	var names := AiScript.project_authored_rules()
	_check("every_invented_rule_is_named_and_sorted",
		Array(names) == [
			"attack_strength_gate",
			"build_score_is_retails_building_score_weight",
			"builds_per_turn_bound",
			"enemy_capital_bonus",
			"march_toward_the_nearest_front", "one_attack_per_turn",
			"seeded_tie_break", "undefended_region_bonus",
		], str(names))
	var stated := true
	for name in names:
		if String(AiScript.PROJECT_AUTHORED_RULES[name]).strip_edges().is_empty():
			stated = false
	_check("every_invented_rule_states_what_it_invents", stated)


# --- the fixture board ---------------------------------------------------------
#
#     Ashfall -- Bramblewold -- Cinderfen -- Dunmarch
#      seat 0      NEUTRAL       seat 1      seat 1
#                                (front)    (interior)
#
# Seat 0 is the human, seat 1 the AI. Bramblewold carries a legendary bonus so
# retail's `BonusPreferenceLegendary` has something to weigh, and Dunmarch is an
# interior region whose army has to march to reach the front at all.

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
				"regions": [
					_region("Ashfall", ["Bramblewold"], {}),
					_region("Bramblewold", ["Ashfall", "Cinderfen"], {"legendary": 10}),
					_region("Cinderfen", ["Bramblewold", "Dunmarch"], {"resource": 5}),
					_region("Dunmarch", ["Cinderfen"], {}),
				],
				"territoryBonuses": [],
			},
		],
		"territoryBonuses": [],
		"regionEffects": [],
		"cities": [],
		"defaultArmies": [
			{
				"scriptingName": "GarrisonArmy1",
				"spawnForTemplates": ["PlayerAlpha", "PlayerBeta"],
				"playerArmy": "TestGarrison",
				"icon": "GarrisonTestIcon",
			},
			# A HERO ARMY, because taking neutral ground is a hero army's job in
			# retail and a garrison army's refusal
			# (`WOTRSCRIPT:WOTR_Tutorial066subtitle`). Without one on the board the
			# opponent has no legal way to expand and the fixture would pin a
			# campaign that cannot move.
			{
				"scriptingName": "HeroArmy1",
				"spawnForTemplates": ["PlayerAlpha", "PlayerBeta"],
				"heroTemplateName": "TestHero",
				"playerArmy": "TestHeroArmy",
				"icon": "HeroTestIcon",
			},
		],
		"playerArmies": [
			{"name": "TestGarrison", "displayNameTag": "LWA:Test",
				"entries": [{"object": "TestSoldier", "quantity": 2}]},
			{"name": "TestHeroArmy", "displayNameTag": "LWA:TestHero",
				"entries": [{"object": "TestHero", "quantity": 1}]},
		],
		"playerTemplates": [
			{"name": "PlayerAlpha", "faction": "FactionMen",
				"startingWorldCp": 1500, "maxWorldCp": 4500,
				"startingHeroCp": 600, "maxHeroCp": 600, "scenarioStartResources": 3000},
			{"name": "PlayerBeta", "faction": "FactionMordor",
				"startingWorldCp": 1500, "maxWorldCp": 4500,
				"startingHeroCp": 600, "maxHeroCp": 600, "scenarioStartResources": 3000},
		],
		"rtsSettings": {"startingCashRts": 1000, "secondsPerReinforcement": 300},
		"gaps": [],
	}


func _region(id: String, connections: Array, bonuses: Dictionary) -> Dictionary:
	var links: Array = []
	for target in connections:
		links.append({"region": String(target), "detourPoints": []})
	return {
		"id": id,
		"displayName": id,
		"mapName": "MAP %s" % id,
		"connections": links,
		"bonuses": bonuses,
		"cpLimit": 1000,
		"allyCpLimit": 400,
		"centerPoint": {"x": 0, "y": 0},
		"buildingSpots": [],
		"garrisonArmySpots": [],
		"heroArmySpots": [],
		"restrictBuildings": [],
	}


## A session on the fixture board with seat 1 an AI. Built by hand rather than
## through `begin()` because the fixture authors no scenario: `world` and `state`
## are the only two things the opponent reads, and every mutation it makes still
## goes through the session's own doors.
## THE SAME BOARD WITH NO CLAIM AVAILABLE, so the march half of a turn and the
## degraded-turn refusal can still be pinned.
##
## Two differences from `_session()`: Bramblewold is HELD BY THE HUMAN rather than
## unowned (so the front army has a real battle to commit and the missing
## auto-resolve tables refuse it by name), and the front army is a garrison. What
## remains is exactly the turn the opponent takes when it cannot fight and cannot
## claim: it marches its interior army to the front.
func _march_session() -> SessionScript:
	var session := _session(true, false)
	session.state.region_owner["Bramblewold"] = 0
	return session


func _session(with_template: bool = true, front_is_hero: bool = true) -> SessionScript:
	var world := WorldScript.new()
	if not world.load_from_dict(_document(), "TestCampaign"):
		printerr("WOTR_AI fixture world did not load: %s" % str(world.errors))
	var state := StateScript.new()
	state.setup(world, [
		{"template": "PlayerAlpha", "team": 1, "controller": StateScript.CONTROLLER_HUMAN},
		{"template": "PlayerBeta", "team": 2, "controller": StateScript.CONTROLLER_AI},
	])
	state.region_owner["Ashfall"] = 0
	state.region_owner["Cinderfen"] = 1
	state.region_owner["Dunmarch"] = 1
	state.place_army(0, "Ashfall", "GarrisonArmy1")
	# TWO ARMIES, because the two halves of a turn need two different armies to
	# show: the one in Cinderfen stands on the front and has a target to weigh,
	# the one in Dunmarch is interior and has to march to reach the front at all.
	# The front army is a HERO army: neutral Bramblewold is the AI's only target
	# and only a hero army can take unowned ground.
	state.place_army(1, "Cinderfen", "HeroArmy1" if front_is_hero else "GarrisonArmy1")
	state.place_army(1, "Dunmarch", "GarrisonArmy1")
	var session := SessionScript.new()
	session.world = world
	session.state = state
	if with_template:
		session.ai.load_from(_write_template("wotr_ai_fixture.ini", FIXTURE_TEMPLATE))
	# Seat 1's turn.
	state.advance_turn()
	return session


# --- the four properties -------------------------------------------------------

func _test_an_ai_seat_takes_a_turn_and_the_board_changes() -> void:
	var session := _session()
	_check("the_fixture_puts_the_ai_seat_on_turn", session.active_seat_is_ai(),
		str(session.state.active_player()))
	var before := session.state.state_hash()
	var report := session.run_ai_turn(HARNESS_MAPS)

	_check("the_ai_turn_reports_ok", bool(report.get("ok", false)),
		str(report.get("refusals", PackedStringArray())))
	_check("the_ai_actually_did_something", bool(report.get("acted", false)), str(report))
	# THE POINT OF THE WHOLE FILE. Without this, the campaign is a sandbox.
	_check("an_ai_turn_changes_the_board", session.state.state_hash() != before)
	# THE OPPONENT EXPANDS ONTO NEUTRAL GROUND, and it does so through exactly the
	# doors a human's clicks go through - `commit_attack()` then
	# `auto_resolve_pending_battle()`, both of which now understand a claim. This
	# file was not edited to teach it; the candidate set IS the screen's own.
	#
	# Before the claim existed this was the campaign's dead end: `attack_targets()`
	# offered Bramblewold, `commit_attack()` opened a battle, `wotr_battle.gd`
	# refused it for want of a defending faction, and the opponent marched in
	# circles forever - on retail's own 52-region board, where EVERY legal target
	# is neutral ground, that is a campaign that cannot move.
	var attack := report.get("attack", {}) as Dictionary
	_check("the_opponent_takes_the_neutral_region",
		session.state.owner_of("Bramblewold") == 1,
		"owner %d" % session.state.owner_of("Bramblewold"))
	_check("the_claiming_hero_army_marched_in",
		session.state.armies_in_region("Bramblewold").size() == 1
			and session.state.armies_in_region("Cinderfen").is_empty(),
		"the claim moves the claiming army and nothing else")
	_check("the_claim_is_reported_as_ground_taken",
		bool(attack.get("captured", false)) and not bool(attack.get("undecided", true))
			and int(attack.get("winner_player", -99)) == 1,
		str(attack))
	_check("a_claim_rolls_no_dice",
		String(attack.get("seed", "unset")).is_empty(),
		"there is nothing to fight, so there is nothing to seed")
	_check("no_transaction_is_left_open_after_a_claim",
		session.state.pending_claim.is_empty() and session.state.pending_battle.is_empty())
	# THE TURN ENDED, exactly once, and control came back to the human. An AI
	# turn that did not end would hand control to nobody and hang the campaign.
	_check("the_ai_ended_its_own_turn_exactly_once",
		int(report.get("turn_index_after", -1)) == int(report.get("turn_index_before", -2)) + 1
			and session.state.active_player() == 0,
		"%d -> %d, active %d" % [
			int(report.get("turn_index_before", -1)), int(report.get("turn_index_after", -1)),
			session.state.active_player()])
	# AND THE LOOP CYCLES. Running the AI again on the human's turn must refuse:
	# it is not this file's seat.
	var refused := session.run_ai_turn(HARNESS_MAPS)
	_check("the_opponent_will_not_move_on_the_humans_turn",
		not bool(refused.get("ok", false)) and not bool(refused.get("acted", false)))

	# THE OTHER HALF OF A TURN, on a board with nothing to claim: an enemy-held
	# front the missing auto-resolve tables refuse, and an interior army that
	# marches to reach the front at all.
	var marcher := _march_session()
	var march_report := marcher.run_ai_turn(HARNESS_MAPS)
	_check("the_interior_army_marched_toward_the_front",
		marcher.state.owner_of("Dunmarch") == 1
			and marcher.state.armies_in_region("Dunmarch").is_empty()
			and marcher.state.armies_in_region("Cinderfen").size() == 2,
		str(march_report.get("marches", [])))
	# A degraded turn SAYS it is degraded. No auto-resolve tables are staged, so
	# the attack refused by name rather than silently not happening.
	_check("an_attack_it_could_not_resolve_is_refused_by_name",
		not (march_report.get("refusals", PackedStringArray()) as PackedStringArray).is_empty(),
		str(march_report.get("refusals", PackedStringArray())))
	# A GARRISON ARMY ON A NEUTRAL BORDER OFFERS NOTHING, which is retail's rule
	# and the reason the list is not simply "every adjacent region I do not own".
	var garrisoned := _session(true, false)
	_check("a_garrison_only_front_offers_no_claim",
		Array(garrisoned.attack_targets("Cinderfen")).is_empty()
			and Array(garrisoned.claim_targets("Cinderfen")).is_empty(),
		str(garrisoned.attack_targets("Cinderfen")))
	# ...and the hero front offers the claim, LABELLED as one, so a screen can say
	# TAKE rather than ATTACK without re-deriving the rule.
	var heroed := _session()
	_check("a_hero_front_offers_the_neutral_region_as_a_claim",
		Array(heroed.attack_targets("Cinderfen")) == ["Bramblewold"]
			and Array(heroed.claim_targets("Cinderfen")) == ["Bramblewold"],
		str(heroed.attack_targets("Cinderfen")))


func _test_the_same_state_and_seed_decide_the_same_twice() -> void:
	var first := _session()
	var second := _session()
	_check("two_independently_built_sessions_start_identical",
		first.state.state_hash() == second.state.state_hash())
	_check("the_seed_is_derived_from_state_and_nothing_else",
		AiScript.seed_for(first.state) == AiScript.seed_for(second.state)
			and not AiScript.seed_for(first.state).is_empty())

	var left := first.run_ai_turn(HARNESS_MAPS)
	var right := second.run_ai_turn(HARNESS_MAPS)
	_check("the_same_state_produces_the_same_seed_in_the_report",
		String(left.get("seed", "a")) == String(right.get("seed", "b")))
	_check("the_same_state_produces_the_same_marches",
		str(left.get("marches", [])) == str(right.get("marches", [])),
		"%s vs %s" % [str(left.get("marches", [])), str(right.get("marches", []))])
	_check("the_same_state_produces_the_same_board",
		first.state.state_hash() == second.state.state_hash())

	# A DIFFERENT STATE MUST BE ALLOWED TO DECIDE DIFFERENTLY - otherwise the
	# check above would pass on an opponent that always does nothing.
	var third := _session()
	third.state.region_owner["Bramblewold"] = 1
	_check("a_different_board_seeds_differently",
		AiScript.seed_for(third.state) != AiScript.seed_for(first.state))

	# THE RANKING ITSELF is a pure function of state: asking twice without moving
	# anything must produce the same order.
	var probe := _session()
	_check("ranking_is_pure",
		str(probe.ai.rank_targets(probe)) == str(probe.ai.rank_targets(probe)))


func _test_the_ai_cannot_move_where_a_human_could_not() -> void:
	var session := _session()
	var state := session.state
	var player := state.active_player()

	# EVERY TARGET IT WOULD CONSIDER passes the same gate the screen's ATTACK
	# button passes - `state.can_attack` - because the candidate set IS the
	# screen's own `staging_regions()` x `attack_targets()`.
	#
	# TWO GATES, because there are two ways to take ground and the screen offers
	# both through one list: an enemy region passes `can_attack`, an unowned one
	# passes `can_claim`. Neither is looser than what a human gets.
	var legal := true
	var ranked := session.ai.rank_targets(session)
	for row in ranked:
		var target := String((row as Dictionary).get("region", ""))
		if not (state.can_attack(player, target) or state.can_claim(player, target)):
			legal = false
	_check("every_considered_target_is_one_the_rules_allow", legal, str(ranked))
	# AND NEITHER GATE IS A RUBBER STAMP: unowned ground is never attackable and
	# owned ground is never claimable, so a target that passed one of them passed
	# the right one.
	var gates_are_disjoint := true
	for row in ranked:
		var target := String((row as Dictionary).get("region", ""))
		if state.can_attack(player, target) and state.can_claim(player, target):
			gates_are_disjoint = false
	_check("the_attack_and_claim_gates_never_both_admit_a_region", gates_are_disjoint,
		str(ranked))
	_check("the_opponent_considered_a_real_target", ranked.size() > 0, str(ranked))
	# It must never consider a region it already owns, nor one it cannot reach.
	var owns_none := true
	for row in ranked:
		if state.owner_of(String((row as Dictionary).get("region", ""))) == player:
			owns_none = false
	_check("the_opponent_never_attacks_itself", owns_none, str(ranked))

	# EVERY MARCH IS ONE GRAPH EDGE INTO A REGION THE SEAT ALREADY OWNS - the
	# same rule `session.movement_targets()` enforces for the human.
	var marches := session.ai.plan_marches(session)
	var marches_legal := not marches.is_empty()
	for step in marches:
		var from_region := String((step as Dictionary).get("from", ""))
		var to_region := String((step as Dictionary).get("to", ""))
		if not session.world.are_adjacent(from_region, to_region):
			marches_legal = false
		if state.owner_of(to_region) != player or state.owner_of(from_region) != player:
			marches_legal = false
		if not Array(session.movement_targets(from_region)).has(to_region):
			marches_legal = false
	_check("every_march_is_one_edge_into_ground_the_seat_already_holds", marches_legal, str(marches))

	# A DETACHED POCKET IS NOT A CRASH AND NOT AN INVENTED TELEPORT. An army in a
	# region with no path to any front simply does not march.
	var pocket := _session()
	pocket.state.region_owner["Bramblewold"] = 1
	pocket.state.region_owner["Ashfall"] = 1
	_check("a_seat_that_touches_no_enemy_plans_no_march",
		pocket.ai.plan_marches(pocket).is_empty(),
		str(pocket.ai.plan_marches(pocket)))


func _test_retail_weights_choose_the_target() -> void:
	# BRAMBLEWOLD carries `legendary = 10`, and the template weights
	# `BonusPreferenceLegendary = 4`, so retail's own arithmetic values it at 40.
	var session := _session()
	var scored := session.ai.score_target(session, 1, "Cinderfen", "Bramblewold")
	_check("retail_weights_multiply_the_documents_own_region_bonuses",
		int(scored.get("retail_score", -1)) == 40, str(scored))
	_check("the_retail_and_project_halves_are_reported_apart",
		int(scored.get("score", 0))
			== int(scored.get("retail_score", 0)) + int(scored.get("project_score", 0)),
		str(scored))
	_check("the_scoring_says_which_retail_key_paid_for_it",
		str(scored.get("reasons", PackedStringArray())).contains("BonusPreferenceLegendary"),
		str(scored.get("reasons", PackedStringArray())))

	# WITHOUT THE TEMPLATE THE RETAIL TERM IS ZERO - not a substituted guess -
	# and the project terms still rank the board so the opponent still plays.
	var bare := _session(false)
	var unweighted := bare.ai.score_target(bare, 1, "Cinderfen", "Bramblewold")
	_check("an_absent_template_contributes_nothing_rather_than_a_guess",
		int(unweighted.get("retail_score", -1)) == 0
			and int(unweighted.get("project_score", 0)) > 0, str(unweighted))
	_check("an_absent_template_is_reported_as_absent",
		not bool((bare.ai.decision_provenance()).get("retail_template_loaded", true)))
	_check("the_opponent_still_takes_its_turn_without_retails_weights",
		bool(bare.run_ai_turn(HARNESS_MAPS).get("acted", false)))

	# THE ATTACK GATE IS THIS PROJECT'S AND IT REFUSES BY NAME. A defender with
	# more command points than the attacker can stage is not attacked.
	var outgunned := _session()
	outgunned.state.region_owner["Bramblewold"] = 0
	outgunned.state.place_army(0, "Bramblewold", "GarrisonArmy1")
	outgunned.state.place_army(0, "Bramblewold", "GarrisonArmy1")
	outgunned.state.place_army(0, "Bramblewold", "GarrisonArmy1")
	var refused := outgunned.ai.score_target(outgunned, 1, "Cinderfen", "Bramblewold")
	_check("the_attack_gate_refuses_a_stronger_defender",
		not bool(refused.get("viable", true))
			and str(refused.get("reasons", PackedStringArray())).contains("attack_strength_gate"),
		str(refused))


func _test_the_hash_round_trips_after_an_ai_turn() -> void:
	var session := _session()
	session.run_ai_turn(HARNESS_MAPS)
	var after := session.state.state_hash()
	var bytes := session.state.snapshot()
	_check("an_ai_turn_still_snapshots", not bytes.is_empty())

	var restored := StateScript.new()
	restored.setup(session.world, [
		{"template": "PlayerAlpha"}, {"template": "PlayerBeta"},
	])
	_check("the_snapshot_restores_after_an_ai_turn", restored.restore(bytes))
	_check("the_strategic_hash_round_trips_after_an_ai_turn",
		restored.state_hash() == after, "%s vs %s" % [restored.state_hash(), after])
	# AND THE RESTORED BOARD PLAYS ON. The AI is stateless with respect to the
	# snapshot - everything it needs it re-derives - so a campaign resumed from
	# disk gets the same opponent it had.
	var resumed := SessionScript.new()
	resumed.world = session.world
	resumed.state = restored
	resumed.ai.load_from(_write_template("wotr_ai_fixture.ini", FIXTURE_TEMPLATE))
	_check("the_opponent_survives_a_snapshot_round_trip",
		str(resumed.ai.rank_targets(resumed)) == str(session.ai.rank_targets(session)))


func _test_the_turn_is_visible() -> void:
	var session := _session()
	var report := session.run_ai_turn(HARNESS_MAPS)
	var narrative := report.get("narrative", PackedStringArray()) as PackedStringArray
	# NEVER SILENT. An unexplained instant turn reads as a bug, and that is the
	# exact complaint this whole lane answers.
	_check("the_turn_produces_a_line_the_player_can_read", not narrative.is_empty(),
		str(narrative))
	_check("the_line_names_the_seat_in_english",
		String(narrative[0]).begins_with("Beta"), String(narrative[0]))
	_check("the_line_names_the_ground_that_changed",
		String(narrative[0]).contains("Bramblewold"), String(narrative[0]))
	# The report carries the before/after hash so a screen (or a log) can prove
	# the board moved rather than asserting it.
	_check("the_report_carries_both_hashes",
		not String(report.get("hash_before", "")).is_empty()
			and String(report.get("hash_after", "")) != String(report.get("hash_before", "")))
	# PROVENANCE RIDES WITH THE TURN, so a player can never be shown an AI move
	# and be left guessing whether retail chose it or this project did.
	var provenance := report.get("provenance", {}) as Dictionary
	_check("the_report_says_where_every_decision_came_from",
		(provenance.get("retail_weights_bound", {}) as Dictionary).size() == 7
			and (provenance.get("project_authored_rules", {}) as Dictionary).size() == 8,
		str(provenance.keys()))

	# A SEAT WITH NOTHING TO DO STILL SAYS SO rather than producing an empty turn.
	var idle := _session()
	idle.state.region_owner["Bramblewold"] = 1
	idle.state.region_owner["Ashfall"] = 1
	var held := idle.run_ai_turn(HARNESS_MAPS)
	_check("a_turn_with_no_move_still_tells_the_player",
		not (held.get("narrative", PackedStringArray()) as PackedStringArray).is_empty()
			and String((held["narrative"] as PackedStringArray)[0]).contains("held its ground"),
		str(held.get("narrative", PackedStringArray())))


func _test_the_ai_refuses_a_seat_that_is_not_its_own() -> void:
	var session := _session()
	# A HUMAN SEAT IS NEVER PLAYED FOR THE PLAYER.
	session.state.turn_index = 0
	_check("a_human_seat_is_not_the_opponents_to_move", not session.active_seat_is_ai())
	var refused := session.run_ai_turn(HARNESS_MAPS)
	_check("moving_for_a_human_seat_is_refused_by_name",
		not bool(refused.get("ok", false))
			and not (refused.get("refusals", PackedStringArray()) as PackedStringArray).is_empty())

	# A DEFEATED AI SEAT IS NOT PLAYED EITHER.
	session.state.advance_turn()
	session.state.set_defeated(1, true)
	_check("a_defeated_ai_seat_is_not_moved", not session.active_seat_is_ai())

	# A BATTLE IN FLIGHT STOPS THE OPPONENT DEAD rather than opening a second one.
	var busy := _session()
	busy.state.pending_battle = {"region": "Bramblewold"}
	var blocked := busy.run_ai_turn(HARNESS_MAPS)
	_check("a_battle_in_flight_stops_the_opponent",
		not bool(blocked.get("ok", false))
			and str(blocked.get("refusals", PackedStringArray())).contains("Bramblewold"),
		str(blocked.get("refusals", PackedStringArray())))

	# `run_ai_turns` MUST TERMINATE. Its bound is what keeps an all-AI campaign
	# from never returning a frame.
	var spectated := _session()
	(spectated.state.players[0] as Dictionary)["controller"] = StateScript.CONTROLLER_AI
	var reports := spectated.run_ai_turns(HARNESS_MAPS, 4)
	_check("an_all_ai_campaign_runs_to_its_bound_and_stops",
		reports.size() > 0 and reports.size() <= 4, str(reports.size()))


# --- against the real living-world document ------------------------------------

## THE SAME OPPONENT ON RETAIL'S OWN 52-REGION MAP, with retail's auto-resolve
## tables behind the battle. This is where an AI attack actually resolves, which
## the fixture cannot show: auto-resolve refuses without retail's damage, armour
## and hitpoint tables, and this project does not ship them.
func _test_against_retail_data() -> bool:
	var located: Dictionary = SessionScript.locate_document([])
	if not bool(located.get("ok", false)):
		print("living-world document : NONE (%s)" % String(located.get("reason", "")))
		return false
	var probe := AutoResolveScript.new()
	var rules_found: Dictionary = probe.locate_and_load([])
	var bound := BindingsScript.new()
	var bindings_found: Dictionary = bound.locate_and_load([])
	if not bool(rules_found.get("ok", false)) or not bool(bindings_found.get("ok", false)):
		print("auto-resolve tables   : NONE")
		return false
	print("living-world document : %s" % String(located.get("path", "")))
	print("auto-resolve tables   : %s" % String(rules_found.get("path", "")))

	var document := located["document"] as Dictionary
	# THE FIRST SEATING WHOSE AI SEAT CAN ACTUALLY FIGHT. Every faction is tried
	# in template order, so the choice is reproducible and the runner reports
	# which one it settled on.
	# EVERY SHIPPED SEAT MUST FIELD A FIGHTABLE ARMY, and this is checked across
	# all of them rather than by finding one that works.
	#
	# A battle that refuses BY NAME is invisible to a player: they press ATTACK and
	# nothing happens. The one seat this used to fail for was the Dwarves, and the
	# cause was a self-referential `DainPlayerArmy` row in retail's own
	# `livingworldbuildableunits.inc` shadowing the correct one (see
	# `_test_a_self_referential_roster_row_is_refused` in the strategic runner) -
	# NOT a missing Dwarven roster: the bindings bundle carries `DwarvenDain` and
	# 23 other Dwarven objects.
	var seats_that_cannot_fight: Array[String] = []
	for candidate in range(8):
		var probe_session := _retail_session(document, candidate)
		if probe_session == null:
			continue
		if not _ai_seat_can_fight(probe_session):
			seats_that_cannot_fight.append(String(
				(probe_session.state.players[1] as Dictionary).get("template", "?")))
	seats_that_cannot_fight.sort()
	_check("every_shipped_seat_fields_armies_the_auto_resolve_bindings_cover",
		seats_that_cannot_fight.is_empty(),
		"these seats would refuse every battle by name: %s" % str(seats_that_cannot_fight))

	var session: SessionScript = null
	var pair := 0
	for candidate in range(8):
		var attempt := _retail_session(document, candidate)
		if attempt != null and _ai_seat_can_fight(attempt):
			session = attempt
			pair = candidate
			break
	if session == null:
		printerr("WOTR_AI no retail seating gives the AI seat armies the bindings cover")
		return false
	print("ai seat                : %s" % String(
		(session.state.players[1] as Dictionary).get("template", "")))

	# RETAIL'S OWN AI TEMPLATE, if it is staged. Reported either way, because an
	# opponent playing without retail's preference weights is a DIFFERENT
	# opponent from one playing with them.
	var template: Dictionary = session.load_ai_template([])
	print("retail AI template    : %s" % (
		String(template.get("path", "")) if bool(template.get("ok", false)) else "NONE"))

	var before := session.state.state_hash()
	var reports := session.run_ai_turns(HARNESS_MAPS, 8)
	_check("the_opponent_takes_turns_on_retails_own_map", reports.size() > 0, str(reports.size()))
	_check("retails_board_changes_under_the_opponent",
		session.state.state_hash() != before)

	var acted := false
	var attacked := false
	var captured := false
	var every_turn_ended := true
	for report in reports:
		if bool(report.get("acted", false)):
			acted = true
		var attack := report.get("attack", {}) as Dictionary
		if not attack.is_empty():
			attacked = true
			if bool(attack.get("captured", false)):
				captured = true
		if int(report.get("turn_index_after", -1)) <= int(report.get("turn_index_before", -1)):
			every_turn_ended = false
	_check("the_opponent_acts_on_retails_map", acted, str(reports.size()))
	_check("every_ai_turn_advanced_the_turn_index", every_turn_ended)
	# THE BATTLE GOES THROUGH THE EXISTING PATH. There is no second settlement
	# path: an AI attack is `commit_attack` + `auto_resolve_pending_battle`,
	# which writes survivors back through `apply_attrition()` exactly as the
	# player's AUTO-RESOLVE button does.
	_check("an_ai_attack_resolves_through_auto_resolve", attacked, str(reports))
	_check("an_ai_attack_takes_ground", captured, str(reports))

	# DETERMINISM ON THE REAL MAP: a second session, seated identically, plays
	# the same turns and lands on the same board.
	var twin := _retail_session(document, pair)
	twin.load_ai_template([])
	var twin_reports := twin.run_ai_turns(HARNESS_MAPS, 8)
	_check("two_retail_sessions_agree_on_the_board_after_eight_ai_turns",
		twin.state.state_hash() == session.state.state_hash(),
		"%s vs %s" % [twin.state.state_hash(), session.state.state_hash()])
	_check("two_retail_sessions_agree_on_every_decision",
		_decision_trace(twin_reports) == _decision_trace(reports))

	# THE MAP OPENS UP, on retail's own board, from retail's own seating, with NO
	# help from the harness.
	#
	# This is the check the owner's complaint reduces to: fourteen turns played,
	# every seat still holding one region, 49 of 52 neutral. `_bring_the_seats_
	# into_contact()` above exists because that was true - without a manufactured
	# border the opponent's every legal target was neutral ground it could not
	# take, and eight AI turns changed nothing. The session below skips the crutch
	# entirely and asserts the opposite: from the shipped seating, the seat grows.
	var unaided := _retail_session(document, pair, false)
	var held_before := unaided.state.regions_owned_by(1).size()
	unaided.load_ai_template([])
	var claimed_regions := 0
	for report in unaided.run_ai_turns(HARNESS_MAPS, 10):
		var attack := report.get("attack", {}) as Dictionary
		if bool(attack.get("captured", false)):
			claimed_regions += 1
	var held_after := unaided.state.regions_owned_by(1).size()
	_check("the_opponent_expands_on_retails_own_board_without_a_manufactured_border",
		held_after > held_before,
		"seat 1 held %d regions, now holds %d" % [held_before, held_after])
	_check("the_expansion_is_reported_turn_by_turn", claimed_regions > 0,
		"%d turns reported taking ground" % claimed_regions)
	_check("the_unaided_board_round_trips_after_ten_turns",
		_round_trips(unaided, pair))

	# AND THE HASH STILL ROUND-TRIPS after all of it.
	var bytes := session.state.snapshot()
	var restored := StateScript.new()
	restored.setup(session.world, _retail_seats(session.world, pair))
	_check("retails_board_round_trips_after_eight_ai_turns",
		restored.restore(bytes) and restored.state_hash() == session.state.state_hash())
	return true


## What the opponent decided, as text, so two runs can be compared without
## comparing presentation strings that a display-name table could change.
func _decision_trace(reports: Array[Dictionary]) -> String:
	var lines: Array[String] = []
	for report in reports:
		var attack := report.get("attack", {}) as Dictionary
		lines.append("seed=%s marches=%s attack=%s->%s" % [
			String(report.get("seed", "")),
			str(report.get("marches", [])),
			String(attack.get("region", "-")),
			str(attack.get("winner_player", "-")),
		])
	return "\n".join(lines)


func _retail_templates(world: WorldScript) -> PackedStringArray:
	var names: Array[String] = []
	for key in world.player_templates.keys():
		var template := world.player_templates[key] as Dictionary
		if int(template.get("starting_world_cp", -1)) > 0:
			names.append(String(key))
	names.sort()
	return PackedStringArray(names)


## Seat 0 human, seat 1 AI: the setup the owner actually plays. `pair` selects
## WHICH two templates, because not every faction's rosters are bound in the
## auto-resolve bindings bundle this checkout ships, and an AI seat whose armies
## field no units cannot fight - a real limitation of the BINDINGS, not of the
## opponent, and one this runner steps past rather than asserts against.
func _retail_seats(world: WorldScript, pair: int = 0) -> Array:
	var names := _retail_templates(world)
	if names.size() < 2:
		return []
	var human := int(pair) % names.size()
	var ai := (human + 1) % names.size()
	return [
		{"template": names[human], "team": 1, "controller": StateScript.CONTROLLER_HUMAN},
		{"template": names[ai], "team": 2, "controller": StateScript.CONTROLLER_AI},
	]


## True when every army the AI seat holds fields auto-resolve units - the
## precondition for an AI attack to resolve at all.
func _ai_seat_can_fight(session: SessionScript) -> bool:
	var found := false
	for key in session.state.armies.keys():
		var army := session.state.armies[key] as Dictionary
		if int(army.get("owner", -1)) != 1:
			continue
		if (army.get("units", []) as Array).is_empty():
			return false
		found = true
	return found


## Snapshot a retail session and restore it into a freshly seated state, asserting
## the hash survives. Factored out because two of the checks above need it and a
## restore that silently differed would be the worst possible pass.
func _round_trips(session: SessionScript, pair: int) -> bool:
	var restored := StateScript.new()
	restored.setup(session.world, _retail_seats(session.world, pair))
	return restored.restore(session.state.snapshot()) \
		and restored.state_hash() == session.state.state_hash()


func _retail_session(document: Dictionary, pair: int = 0, make_contact: bool = true) -> SessionScript:
	var session := SessionScript.new()
	session.load_auto_resolve([])
	var probe := WorldScript.new()
	if not probe.load_from_dict(document, ""):
		return null
	var seats := _retail_seats(probe, pair)
	if seats.is_empty():
		return null
	# `is_freeform()` reads the session's own world, so the probe is bound first.
	# `begin()` replaces it with the one it loads for itself.
	session.world = probe
	var scenario := ""
	var freeform := false
	for name in probe.scenario_names:
		if session.is_freeform(String(name)):
			scenario = String(name)
			freeform = true
			break
	if scenario.is_empty():
		# EVERY SHIPPED ROTWK SCENARIO AUTHORS ITS OWN OWNERSHIP, which is the
		# setup the owner actually plays: the seats start where retail put them
		# and `start_regions` must not be passed alongside.
		for name in probe.scenario_names:
			if probe.scenario(String(name)).get("ownership_sets", []).size() >= seats.size():
				scenario = String(name)
				break
	if scenario.is_empty():
		return null
	var chosen := PackedStringArray()
	if freeform:
		var starts := SessionScript.default_start_spots(document, scenario)
		if starts.size() < seats.size():
			return null
		for index in range(seats.size()):
			chosen.append(starts[index])
	if not session.begin(document, probe.campaign_name, scenario, seats, {}, chosen):
		printerr("WOTR_AI retail seating refused: %s" % str(session.refusals))
		return null
	if make_contact:
		_bring_the_seats_into_contact(session)
	# Seat 1's turn.
	session.state.advance_turn()
	return session


## PUT THE TWO SEATS ON THE SAME BORDER.
##
## Retail seats each side deep inside its own territory with neutral ground
## between them, so on the shipped seating the two seats do not touch and the
## opponent's first several turns are CLAIMS rather than battles. This helper
## manufactures a shared border so a BATTLE can be pinned inside eight turns
## without waiting for the two seats to grow into each other.
##
## It used to exist for a different and much worse reason - neutral ground could
## not be taken at all, so without a manufactured border eight AI turns changed
## nothing - and that is no longer true: the check
## `the_opponent_expands_on_retails_own_board_without_a_manufactured_border`
## above runs the same seating with this helper SKIPPED and asserts the seat
## grows anyway.
##
## The border is made through the same authoritative-state door a conquest uses
## (`transfer_region`), and DETERMINISTICALLY: the lowest-sorted neutral
## neighbour of the lowest-sorted region the AI seat owns. This is the test
## constructing a board, not the opponent inventing a capture.
func _bring_the_seats_into_contact(session: SessionScript) -> void:
	var state := session.state
	for region_id in state.regions_owned_by(1):
		for neighbour in session.world.neighbours(region_id):
			if state.owner_of(neighbour) != StateScript.NEUTRAL:
				continue
			state.transfer_region(neighbour, 0)
			return


# --- harness -------------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WOTR_AI PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_AI FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

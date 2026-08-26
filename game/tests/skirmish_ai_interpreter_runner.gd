extends SceneTree
## Q83 phase 2: the sim's skirmish-AI interpreter compiles the raw authored
## document into typed per-side plans, and refuses loudly by name on every
## malformed shape. Runs against synthetic fixtures AND the selected pack's
## real document.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = SimScript.new()

	# --- refusals configure nothing -------------------------------------
	_check(not bool(sim.configure_skirmish_ai({}).get("ok", true)), "empty document refuses")
	_check(not bool(sim.skirmish_ai_configured), "refusal leaves the sim unconfigured")
	_check(
		String(sim.configure_skirmish_ai({"schema": "wrong", "schemaVersion": 0}).get("reason", ""))
			.contains("schema"),
		"wrong schema refuses by name"
	)
	var no_side := _fixture_document()
	((no_side["armies"] as Dictionary)["FixtureArmy"] as Dictionary).erase("side")
	_check(
		String(sim.configure_skirmish_ai(no_side).get("reason", "")).contains("Side"),
		"army without an authored Side refuses by name"
	)
	var bad_phase := _fixture_document()
	var bad_member: Dictionary = (((bad_phase["armies"] as Dictionary)["FixtureArmy"] as Dictionary)["armyMembers"] as Array)[0]
	((bad_member["fields"] as Dictionary)["PercentageOfArmyPhase1"] as Dictionary)["value"] = "not-a-number"
	_check(
		String(sim.configure_skirmish_ai(bad_phase).get("reason", "")).contains("unmeasured"),
		"unmeasured phase percentage refuses by name"
	)

	# --- synthetic fixture compiles typed -------------------------------
	var accepted: Dictionary = sim.configure_skirmish_ai(_fixture_document())
	_check(bool(accepted.get("ok", false)), "well-formed fixture configures")
	_check(bool(sim.skirmish_ai_configured), "configuration flag is set")
	var plan: Dictionary = sim.skirmish_ai_plan_for_side("MEN")
	_check(not plan.is_empty(), "side lookup folds case")
	var member := (plan.get("members", []) as Array)[0] as Dictionary
	_check(String(member.get("unit", "")) == "FixtureHorde", "member unit id survives")
	_check(member.get("phase_percentages", []) as Array == [40.0, 40.0, 0.0], "phase percentages parse; an absent phase records 0.0")
	_check(plan.get("hero_build_order", []) as Array == ["FixtureHero"], "hero build order tokens survive")
	var normal := (sim.skirmish_ai_difficulty as Dictionary).get("NORMAL", {}) as Dictionary
	_check(normal.get("EconomyUpgradeProbability", []) as Array == [10.0, 100.0], "difficulty ratio '10 : 100' parses to a pair")
	_check(
		(plan.get("must_use_command_point_percentage", []) as Array)[0] == 90.0,
		"'90%' parses keeping authored magnitude"
	)

	# --- the real selected-pack document --------------------------------
	var content_db = root.get_node_or_null("ContentDB")
	if content_db != null and not (content_db.skirmish_ai_runtime as Dictionary).is_empty():
		var live = SimScript.new()
		var live_result: Dictionary = live.configure_skirmish_ai(content_db.skirmish_ai_runtime)
		_check(bool(live_result.get("ok", false)), "the selected pack's real document configures")
		_check(int(live_result.get("sides", 0)) >= 6, "real document yields >= 6 sides")
		var men: Dictionary = live.skirmish_ai_plan_for_side("Men")
		_check(not men.is_empty(), "real document has a Men plan")
		_check(not (men.get("members", []) as Array).is_empty(), "Men plan has authored members")
		var live_difficulty := live.skirmish_ai_difficulty as Dictionary
		_check(live_difficulty.has("NORMAL") and live_difficulty.has("EASY"), "real difficulty rows compile (EASY, NORMAL)")
		_check(not (live.skirmish_ai_brutal_cheats as Dictionary).is_empty(), "brutal cheats compile from the only authored cheat block")
	else:
		_check(false, "selected pack surfaces a skirmish-AI document for the live half of this runner")

	# --- production consumption: authored composition drives the choice --
	var ai_sim = SimScript.new()
	ai_sim.configure_skirmish_ai(_consumption_document())
	ai_sim._team_descriptors[0] = {"faction": "fixture-men"}
	ai_sim._rules["retail_faction_sides"] = {"fixture-men": "Men"}
	ai_sim._unit_production_rules = {
		"FixtureHorde": {"producer_kind": "barracks"},
		"FixtureArcher": {"producer_kind": "archery_range"},
	}
	var first: Dictionary = ai_sim._skirmish_ai_subsystem().authored_ai_queue_choice(0)
	_check(bool(first.get("ok", false)), "consumption: an empty army asks for a unit")
	_check(String(first.get("unit_type", "")) == "FixtureArcher", "consumption: the 60%% member is most deficient first")
	_check(int(first.get("phase", 0)) == 1, "consumption: tick 0 is phase 1 (Rush)")
	_check((first.get("untrainable", []) as Array) == ["FixtureUnportable"], "consumption: the unportable member is a NAMED receipt, not silence")
	# Field two archers and no horde: the horde becomes the deficit.
	ai_sim.entities[1] = {"id": 1, "team": 0, "unit_type": "FixtureArcher", "health": 10}
	ai_sim.entities[2] = {"id": 2, "team": 0, "unit_type": "FixtureArcher", "health": 10}
	var second: Dictionary = ai_sim._skirmish_ai_subsystem().authored_ai_queue_choice(0)
	_check(String(second.get("unit_type", "")) == "FixtureHorde", "consumption: composition deficit flips the choice")
	# Phase progression: past the authored rush duration the army is phase 2,
	# where the horde has no percentage and the archer is the only candidate.
	ai_sim.tick_index = int(300.0 / float(ai_sim.TICK_SECONDS)) + 1
	var third: Dictionary = ai_sim._skirmish_ai_subsystem().authored_ai_queue_choice(0)
	_check(int(third.get("phase", 0)) == 2, "consumption: elapsed time enters phase 2")
	_check(String(third.get("unit_type", "")) == "FixtureArcher", "consumption: phase-2 composition has only the archer")
	# --- HeroBuildOrder consumption --------------------------------------
	ai_sim._unit_production_rules["FixtureHero"] = {"producer_kind": "fortress", "category": "hero"}
	ai_sim._unit_production_rules["FixtureHeroTwo"] = {"producer_kind": "fortress", "category": "hero"}
	var hero_first: Dictionary = ai_sim._skirmish_ai_subsystem().authored_hero_choice(0)
	_check(bool(hero_first.get("ok", false)), "hero order: a trainable authored hero is offered")
	_check(String(hero_first.get("unit_type", "")) == "FixtureHero", "hero order: the FIRST trainable hero wins")
	_check((hero_first.get("untrainable", []) as Array) == ["FixtureUnportableHero"], "hero order: unportable heroes are NAMED receipts")
	ai_sim._completed_hero_identities["0:FixtureHero"] = true
	var hero_second: Dictionary = ai_sim._skirmish_ai_subsystem().authored_hero_choice(0)
	_check(String(hero_second.get("unit_type", "")) == "FixtureHeroTwo", "hero order: a fielded hero advances the order")
	ai_sim._completed_hero_identities["0:FixtureHeroTwo"] = true
	var hero_done: Dictionary = ai_sim._skirmish_ai_subsystem().authored_hero_choice(0)
	_check(
		not bool(hero_done.get("ok", true)) and String(hero_done.get("reason", "")).contains("no trainable authored hero"),
		"hero order: an exhausted order refuses by name"
	)

	# --- authored difficulty odds ----------------------------------------
	_check(
		ai_sim._skirmish_ai_subsystem().authored_special_power_odds(0) == [10.0, 150.0],
		"difficulty odds: the NORMAL tier's special-power pair reaches the AI"
	)
	var odds_less = SimScript.new()
	_check(
		(odds_less._skirmish_ai_subsystem().authored_special_power_odds(0) as Array).is_empty(),
		"difficulty odds: an unconfigured sim answers no odds"
	)

	var sideless_sim = SimScript.new()
	sideless_sim.configure_skirmish_ai(_consumption_document())
	var refused: Dictionary = sideless_sim._skirmish_ai_subsystem().authored_ai_queue_choice(0)
	_check(
		not bool(refused.get("ok", true)) and String(refused.get("reason", "")).contains("faction"),
		"consumption: a team without a retail side refuses by name"
	)

	print("SKIRMISH_AI_INTERPRETER_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _consumption_document() -> Dictionary:
	var document := _fixture_document()
	var army := (document["armies"] as Dictionary)["FixtureArmy"] as Dictionary
	(army["fields"] as Dictionary)["PhaseDuration_Rush"] = {"value": "300.0"}
	army["heroBuildOrder"] = {"value": ["FixtureUnportableHero", "FixtureHero", "FixtureHeroTwo"]}
	army["armyMembers"] = [
		{
			"name": {"value": "FixtureHorde_Member"},
			"fields": {
				"Unit": {"value": "FixtureHorde"},
				"PercentageOfArmyPhase1": {"value": "40.0"},
			},
		},
		{
			"name": {"value": "FixtureArcher_Member"},
			"fields": {
				"Unit": {"value": "FixtureArcher"},
				"PercentageOfArmyPhase1": {"value": "60.0"},
				"PercentageOfArmyPhase2": {"value": "100.0"},
			},
		},
		{
			"name": {"value": "FixtureUnportable_Member"},
			"fields": {
				"Unit": {"value": "FixtureUnportable"},
				"PercentageOfArmyPhase1": {"value": "10.0"},
			},
		},
	]
	return document


func _fixture_document() -> Dictionary:
	return {
		"schema": "openbfme.skirmish-ai",
		"schemaVersion": 0,
		"globals": {},
		"census": {"armyDefinitionCount": 1},
		"combatChains": [],
		"aiBases": [],
		"difficultyTuning": {
			"NormalTuning": {
				"fields": {
					"Difficulty": {"value": "NORMAL"},
					"EconomyUpgradeProbability": {"value": "10 : 100"},
					"SpecialPowerActivationProbability": {"value": "10 : 150"},
				},
			},
		},
		"brutalDifficultyCheats": {
			"fields": {"BuildCostReduction": {"value": "15%"}},
		},
		"armies": {
			"FixtureArmy": {
				"side": {"value": "Men"},
				"heroBuildOrder": {"value": ["FixtureHero"]},
				"offensiveBuildings": {"value": []},
				"fields": {
					"Side": {"value": "Men"},
					"MustUseCommandPointPercentage_Phase1": {"value": "90%"},
					"PhaseDuration_Rush": {"value": "270.0"},
				},
				"armyMembers": [
					{
						"name": {"value": "FixtureHorde_Member"},
						"fields": {
							"Unit": {"value": "FixtureHorde"},
							"PercentageOfArmyPhase1": {"value": "40.0"},
							"PercentageOfArmyPhase2": {"value": "40.0"},
						},
					},
				],
			},
		},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("SKIRMISH_AI_INTERPRETER PASS %s" % label)
	else:
		failed += 1
		push_error("SKIRMISH_AI_INTERPRETER FAIL %s" % label)

extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	_test_skirmish_stats_exclude_multiplayer_ids()
	_test_multiplayer_stats_exclude_skirmish_ids()
	var empty = Sim.new()
	_check(not empty._authoritative_state().has("cah_award_tallies"), "hero-less snapshots keep the pinned byte shape")
	print("CAH_AWARDS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _award_sim(session_mode: String):
	var sim = Sim.new()
	if session_mode != "":
		sim._rules["session_mode"] = session_mode
	var hero_id := "CreateAHero__0123456789abcdef01234567"
	sim._cah_award_contracts[hero_id] = {
		"heroId": "0123456789abcdef01234567",
		"ownerTeam": 0,
		"trackingStats": {},
		"ownedAwards": [],
		"eligibleAwards": ["Vanquisher", "HeroHunter", "SiegeMaster"],
		"awardDefinitions": [
			{"awardId": "Vanquisher", "triggers": [{"statIds": ["ENEMIES_KILLED"], "threshold": 1}]},
			{"awardId": "HeroHunter", "triggers": [{"statIds": ["HEROS_KILLED"], "threshold": 1}]},
			{"awardId": "SiegeMaster", "triggers": [{"statIds": ["MP_KEEPS_DESTROYED"], "threshold": 1}]},
		],
	}
	sim.entities[1] = {"unit_type": hero_id, "team": 0, "health": 100}
	return sim


func _exercise_kills(sim) -> Dictionary:
	var hero_id := "CreateAHero__0123456789abcdef01234567"
	# No ExperienceLevel contract exists for the victim: the award count must
	# happen before the XP path returns for unauthored evidence.
	sim._award_member_kill_experience(1, {"unit_type": "enemy.hero", "category": "hero", "team": 1})
	sim._award_member_kill_experience(1, {"unit_type": "CreateAHero__enemy", "category": "hero", "team": 1})
	sim._record_cah_structure_kill(1, {"structure_kind": "fortress", "team": 1})
	return sim._cah_award_tallies[hero_id] as Dictionary


func _test_skirmish_stats_exclude_multiplayer_ids() -> void:
	var sim = _award_sim("")
	var stats := _exercise_kills(sim)
	_check(stats.get("ENEMIES_KILLED") == 2, "skirmish member kills increment ENEMIES_KILLED")
	_check(stats.get("HEROS_KILLED") == 2, "skirmish hero categories increment retail-spelled HEROS_KILLED")
	_check(not stats.has("MP_CREATE_A_HEROES_KILLED"), "skirmish excludes MP_CREATE_A_HEROES_KILLED")
	_check(not stats.has("MP_KEEPS_DESTROYED"), "skirmish excludes MP_KEEPS_DESTROYED")
	sim.winner = 0
	sim._evaluate_cah_match_awards()
	stats = sim.cah_award_results.values()[0]["trackingStats"] as Dictionary
	_check(stats.get("HERO_VICTORY_COUNT_SKIRMISH") == 1, "skirmish credits HERO_VICTORY_COUNT_SKIRMISH")
	_check(not stats.has("HERO_VICTORY_COUNT_OPENPLAY_MP"), "skirmish excludes HERO_VICTORY_COUNT_OPENPLAY_MP")


func _test_multiplayer_stats_exclude_skirmish_ids() -> void:
	var sim = _award_sim("openplay-mp")
	var hero_id := "CreateAHero__0123456789abcdef01234567"
	var stats := _exercise_kills(sim)
	_check(stats.get("MP_CREATE_A_HEROES_KILLED") == 1, "multiplayer credits MP_CREATE_A_HEROES_KILLED")
	_check(stats.get("MP_KEEPS_DESTROYED") == 1, "multiplayer credits MP_KEEPS_DESTROYED")
	sim.winner = 1
	sim._evaluate_cah_match_awards()
	var result: Dictionary = sim.cah_award_results[hero_id]
	stats = result.get("trackingStats", {}) as Dictionary
	_check(stats.get("HERO_DEFEAT_COUNT_OPENPLAY_MP") == 1, "multiplayer credits HERO_DEFEAT_COUNT_OPENPLAY_MP")
	_check(not stats.has("HERO_DEFEAT_COUNT_SKIRMISH"), "multiplayer excludes HERO_DEFEAT_COUNT_SKIRMISH")
	_check((result.get("awards", []) as Array).size() == 3, "eligible multiplayer award triggers evaluate at match end")


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("CAH_AWARDS_FAIL %s" % label)
